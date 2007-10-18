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

#ifdef L_divI
/* ROUTINES:	$$divI, $$divoI

   Single precision divide for signed binary integers.

   The quotient is truncated towards zero.
   The sign of the quotient is the XOR of the signs of the dividend and
   divisor.
   Divide by zero is trapped.
   Divide of -2**31 by -1 is trapped for $$divoI but not for $$divI.

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
   .		divisor is zero  (traps with ADDIT,=  0,25,0)
   .		dividend==-2**31  and divisor==-1 and routine is $$divoI
   .				 (traps with ADDO  26,25,0)
   .	Changes memory at the following places:
   .		NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Branchs to other millicode routines using BE
   .		$$div_# for # being 2,3,4,5,6,7,8,9,10,12,14,15
   .
   .	For selected divisors, calls a divide by constant routine written by
   .	Karl Pettis.  Eligible divisors are 1..15 excluding 11 and 13.
   .
   .	The only overflow case is -2**31 divided by -1.
   .	Both routines return -2**31 but only $$divoI traps.  */

RDEFINE(temp,r1)
RDEFINE(retreg,ret1)	/*  r29 */
RDEFINE(temp1,arg0)
	SUBSPA_MILLI_DIV
	ATTR_MILLI
	.import $$divI_2,millicode
	.import $$divI_3,millicode
	.import $$divI_4,millicode
	.import $$divI_5,millicode
	.import $$divI_6,millicode
	.import $$divI_7,millicode
	.import $$divI_8,millicode
	.import $$divI_9,millicode
	.import $$divI_10,millicode
	.import $$divI_12,millicode
	.import $$divI_14,millicode
	.import $$divI_15,millicode
	.export $$divI,millicode
	.export	$$divoI,millicode
	.proc
	.callinfo	millicode
	.entry
GSYM($$divoI)
	comib,=,n  -1,arg1,LREF(negative1)	/*  when divisor == -1 */
GSYM($$divI)
	ldo	-1(arg1),temp		/*  is there at most one bit set ? */
	and,<>	arg1,temp,r0		/*  if not, don't use power of 2 divide */
	addi,>	0,arg1,r0		/*  if divisor > 0, use power of 2 divide */
	b,n	LREF(neg_denom)
LSYM(pow2)
	addi,>=	0,arg0,retreg		/*  if numerator is negative, add the */
	add	arg0,temp,retreg	/*  (denominaotr -1) to correct for shifts */
	extru,=	arg1,15,16,temp		/*  test denominator with 0xffff0000 */
	extrs	retreg,15,16,retreg	/*  retreg = retreg >> 16 */
	or	arg1,temp,arg1		/*  arg1 = arg1 | (arg1 >> 16) */
	ldi	0xcc,temp1		/*  setup 0xcc in temp1 */
	extru,= arg1,23,8,temp		/*  test denominator with 0xff00 */
	extrs	retreg,23,24,retreg	/*  retreg = retreg >> 8 */
	or	arg1,temp,arg1		/*  arg1 = arg1 | (arg1 >> 8) */
	ldi	0xaa,temp		/*  setup 0xaa in temp */
	extru,= arg1,27,4,r0		/*  test denominator with 0xf0 */
	extrs	retreg,27,28,retreg	/*  retreg = retreg >> 4 */
	and,=	arg1,temp1,r0		/*  test denominator with 0xcc */
	extrs	retreg,29,30,retreg	/*  retreg = retreg >> 2 */
	and,=	arg1,temp,r0		/*  test denominator with 0xaa */
	extrs	retreg,30,31,retreg	/*  retreg = retreg >> 1 */
	MILLIRETN
LSYM(neg_denom)
	addi,<	0,arg1,r0		/*  if arg1 >= 0, it's not power of 2 */
	b,n	LREF(regular_seq)
	sub	r0,arg1,temp		/*  make denominator positive */
	comb,=,n  arg1,temp,LREF(regular_seq)	/*  test against 0x80000000 and 0 */
	ldo	-1(temp),retreg		/*  is there at most one bit set ? */
	and,=	temp,retreg,r0		/*  if so, the denominator is power of 2 */
	b,n	LREF(regular_seq)
	sub	r0,arg0,retreg		/*  negate numerator */
	comb,=,n arg0,retreg,LREF(regular_seq) /*  test against 0x80000000 */
	copy	retreg,arg0		/*  set up arg0, arg1 and temp	*/
	copy	temp,arg1		/*  before branching to pow2 */
	b	LREF(pow2)
	ldo	-1(arg1),temp
LSYM(regular_seq)
	comib,>>=,n 15,arg1,LREF(small_divisor)
	add,>=	0,arg0,retreg		/*  move dividend, if retreg < 0, */
LSYM(normal)
	subi	0,retreg,retreg		/*    make it positive */
	sub	0,arg1,temp		/*  clear carry,  */
					/*    negate the divisor */
	ds	0,temp,0		/*  set V-bit to the comple- */
					/*    ment of the divisor sign */
	add	retreg,retreg,retreg	/*  shift msb bit into carry */
	ds	r0,arg1,temp		/*  1st divide step, if no carry */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  2nd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  3rd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  4th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  5th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  6th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  7th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  8th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  9th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  10th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  11th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  12th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  13th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  14th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  15th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  16th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  17th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  18th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  19th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  20th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  21st divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  22nd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  23rd divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  24th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  25th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  26th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  27th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  28th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  29th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  30th divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  31st divide step */
	addc	retreg,retreg,retreg	/*  shift retreg with/into carry */
	ds	temp,arg1,temp		/*  32nd divide step, */
	addc	retreg,retreg,retreg	/*  shift last retreg bit into retreg */
	xor,>=	arg0,arg1,0		/*  get correct sign of quotient */
	  sub	0,retreg,retreg		/*    based on operand signs */
	MILLIRETN
	nop

LSYM(small_divisor)

#if defined(CONFIG_64BIT)
/*  Clear the upper 32 bits of the arg1 register.  We are working with	*/
/*  small divisors (and 32-bit integers)   We must not be mislead  */
/*  by "1" bits left in the upper 32 bits.  */
	depd %r0,31,32,%r25
#endif
	blr,n	arg1,r0
	nop
/*  table for divisor == 0,1, ... ,15 */
	addit,=	0,arg1,r0	/*  trap if divisor == 0 */
	nop
	MILLIRET		/*  divisor == 1 */
	copy	arg0,retreg
	MILLI_BEN($$divI_2)	/*  divisor == 2 */
	nop
	MILLI_BEN($$divI_3)	/*  divisor == 3 */
	nop
	MILLI_BEN($$divI_4)	/*  divisor == 4 */
	nop
	MILLI_BEN($$divI_5)	/*  divisor == 5 */
	nop
	MILLI_BEN($$divI_6)	/*  divisor == 6 */
	nop
	MILLI_BEN($$divI_7)	/*  divisor == 7 */
	nop
	MILLI_BEN($$divI_8)	/*  divisor == 8 */
	nop
	MILLI_BEN($$divI_9)	/*  divisor == 9 */
	nop
	MILLI_BEN($$divI_10)	/*  divisor == 10 */
	nop
	b	LREF(normal)		/*  divisor == 11 */
	add,>=	0,arg0,retreg
	MILLI_BEN($$divI_12)	/*  divisor == 12 */
	nop
	b	LREF(normal)		/*  divisor == 13 */
	add,>=	0,arg0,retreg
	MILLI_BEN($$divI_14)	/*  divisor == 14 */
	nop
	MILLI_BEN($$divI_15)	/*  divisor == 15 */
	nop

LSYM(negative1)
	sub	0,arg0,retreg	/*  result is negation of dividend */
	MILLIRET
	addo	arg0,arg1,r0	/*  trap iff dividend==0x80000000 && divisor==-1 */
	.exit
	.procend
	.end
#endif
