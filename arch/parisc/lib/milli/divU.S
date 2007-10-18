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

#ifdef L_divU
/* ROUTINE:	$$divU
   .
   .	Single precision divide for unsigned integers.
   .
   .	Quotient is truncated towards zero.
   .	Traps on divide by zero.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	arg1 ==	divisor
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	quotient

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions:
   .		divisor is zero
   .	Changes memory at the following places:
   .		NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Branchs to other millicode routines using BE:
   .		$$divU_# for 3,5,6,7,9,10,12,14,15
   .
   .	For selected small divisors calls the special divide by constant
   .	routines written by Karl Pettis.  These are: 3,5,6,7,9,10,12,14,15.  */

RDEFINE(temp,r1)
RDEFINE(retreg,ret1)	/* r29 */
RDEFINE(temp1,arg0)
	SUBSPA_MILLI_DIV
	ATTR_MILLI
	.export $$divU,millicode
	.import $$divU_3,millicode
	.import $$divU_5,millicode
	.import $$divU_6,millicode
	.import $$divU_7,millicode
	.import $$divU_9,millicode
	.import $$divU_10,millicode
	.import $$divU_12,millicode
	.import $$divU_14,millicode
	.import $$divU_15,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$divU)
/* The subtract is not nullified since it does no harm and can be used
   by the two cases that branch back to "normal".  */
	ldo	-1(arg1),temp		/* is there at most one bit set ? */
	and,=	arg1,temp,r0		/* if so, denominator is power of 2 */
	b	LREF(regular_seq)
	addit,=	0,arg1,0		/* trap for zero dvr */
	copy	arg0,retreg
	extru,= arg1,15,16,temp		/* test denominator with 0xffff0000 */
	extru	retreg,15,16,retreg	/* retreg = retreg >> 16 */
	or	arg1,temp,arg1		/* arg1 = arg1 | (arg1 >> 16) */
	ldi	0xcc,temp1		/* setup 0xcc in temp1 */
	extru,= arg1,23,8,temp		/* test denominator with 0xff00 */
	extru	retreg,23,24,retreg	/* retreg = retreg >> 8 */
	or	arg1,temp,arg1		/* arg1 = arg1 | (arg1 >> 8) */
	ldi	0xaa,temp		/* setup 0xaa in temp */
	extru,= arg1,27,4,r0		/* test denominator with 0xf0 */
	extru	retreg,27,28,retreg	/* retreg = retreg >> 4 */
	and,=	arg1,temp1,r0		/* test denominator with 0xcc */
	extru	retreg,29,30,retreg	/* retreg = retreg >> 2 */
	and,=	arg1,temp,r0		/* test denominator with 0xaa */
	extru	retreg,30,31,retreg	/* retreg = retreg >> 1 */
	MILLIRETN
	nop	
LSYM(regular_seq)
	comib,>=  15,arg1,LREF(special_divisor)
	subi	0,arg1,temp		/* clear carry, negate the divisor */
	ds	r0,temp,r0		/* set V-bit to 1 */
LSYM(normal)
	add	arg0,arg0,retreg	/* shift msb bit into carry */
	ds	r0,arg1,temp		/* 1st divide step, if no carry */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 2nd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 3rd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 4th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 5th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 6th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 7th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 8th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 9th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 10th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 11th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 12th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 13th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 14th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 15th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 16th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 17th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 18th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 19th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 20th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 21st divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 22nd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 23rd divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 24th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 25th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 26th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 27th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 28th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 29th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 30th divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 31st divide step */
	addc	retreg,retreg,retreg	/* shift retreg with/into carry */
	ds	temp,arg1,temp		/* 32nd divide step, */
	MILLIRET
	addc	retreg,retreg,retreg	/* shift last retreg bit into retreg */

/* Handle the cases where divisor is a small constant or has high bit on.  */
LSYM(special_divisor)
/*	blr	arg1,r0 */
/*	comib,>,n  0,arg1,LREF(big_divisor) ; nullify previous instruction */

/* Pratap 8/13/90. The 815 Stirling chip set has a bug that prevents us from
   generating such a blr, comib sequence. A problem in nullification. So I
   rewrote this code.  */

#if defined(CONFIG_64BIT)
/* Clear the upper 32 bits of the arg1 register.  We are working with
   small divisors (and 32-bit unsigned integers)   We must not be mislead
   by "1" bits left in the upper 32 bits.  */
	depd %r0,31,32,%r25
#endif
	comib,>	0,arg1,LREF(big_divisor)
	nop
	blr	arg1,r0
	nop

LSYM(zero_divisor)	/* this label is here to provide external visibility */
	addit,=	0,arg1,0		/* trap for zero dvr */
	nop
	MILLIRET			/* divisor == 1 */
	copy	arg0,retreg
	MILLIRET			/* divisor == 2 */
	extru	arg0,30,31,retreg
	MILLI_BEN($$divU_3)		/* divisor == 3 */
	nop
	MILLIRET			/* divisor == 4 */
	extru	arg0,29,30,retreg
	MILLI_BEN($$divU_5)		/* divisor == 5 */
	nop
	MILLI_BEN($$divU_6)		/* divisor == 6 */
	nop
	MILLI_BEN($$divU_7)		/* divisor == 7 */
	nop
	MILLIRET			/* divisor == 8 */
	extru	arg0,28,29,retreg
	MILLI_BEN($$divU_9)		/* divisor == 9 */
	nop
	MILLI_BEN($$divU_10)		/* divisor == 10 */
	nop
	b	LREF(normal)		/* divisor == 11 */
	ds	r0,temp,r0		/* set V-bit to 1 */
	MILLI_BEN($$divU_12)		/* divisor == 12 */
	nop
	b	LREF(normal)		/* divisor == 13 */
	ds	r0,temp,r0		/* set V-bit to 1 */
	MILLI_BEN($$divU_14)		/* divisor == 14 */
	nop
	MILLI_BEN($$divU_15)		/* divisor == 15 */
	nop

/* Handle the case where the high bit is on in the divisor.
   Compute:	if( dividend>=divisor) quotient=1; else quotient=0;
   Note:	dividend>==divisor iff dividend-divisor does not borrow
   and		not borrow iff carry.  */
LSYM(big_divisor)
	sub	arg0,arg1,r0
	MILLIRET
	addc	r0,r0,retreg
	.exit
	.procend
	.end
#endif
